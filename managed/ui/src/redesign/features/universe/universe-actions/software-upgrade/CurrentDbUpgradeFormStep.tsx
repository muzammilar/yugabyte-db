import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';
import { DbUpgradeFormStep } from './constants';
import { DbVersionStep } from './upgrade-steps/DbVersionStep';
import { UpgradeMethodStep } from './upgrade-steps/UpgradeMethodStep';
import { UpgradePlanStep } from './upgrade-steps/UpgradePlanStep';
import { UpgradePaceStep } from './upgrade-steps/UpgradePaceStep';

interface CurrentDbUpgradeFormStepProps {
  currentFormStep: DbUpgradeFormStep;
}

export const CurrentDbUpgradeFormStep = ({ currentFormStep }: CurrentDbUpgradeFormStepProps) => {
  switch (currentFormStep) {
    case DbUpgradeFormStep.DB_VERSION:
      return <DbVersionStep />;
    case DbUpgradeFormStep.UPGRADE_METHOD:
      return <UpgradeMethodStep />;
    case DbUpgradeFormStep.UPGRADE_PLAN:
      return <UpgradePlanStep />;
    case DbUpgradeFormStep.UPGRADE_PACE:
      return <UpgradePaceStep />;
    default:
      return assertUnreachableCase(currentFormStep);
  }
};
